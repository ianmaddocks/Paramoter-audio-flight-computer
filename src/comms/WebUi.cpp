#include "WebUi.h"
#include "Log.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
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
#include "comms/WebPage.h"
#include "nav/Geo.h"
#include "sensors/Mic.h"
#include "test/Sim.h"


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

// Server Wi-Fi runs alongside the vario AP only during its configured boot window.
enum class ServerWifi : uint8_t { Off, Connecting, Connected };
ServerWifi serverWifi = ServerWifi::Off;
uint32_t serverAttemptStartedMs = 0;  // start of the current search for the server network
const char* serverWhyOff = "not set";
char serverSsidUsed[33] = "";
char serverPassUsed[65] = "";

void serverStop(const char* why) {
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  serverWifi = ServerWifi::Off;
  serverWhyOff = why;
  LOGI("WEB", "Server Wi-Fi off (%s)", why);
}

void serverUpdate(uint32_t now) {
  if (serverWifi == ServerWifi::Off) return;
  bool up = WiFi.status() == WL_CONNECTED;
  if (serverWifi == ServerWifi::Connecting && up) {
    serverWifi = ServerWifi::Connected;
    LOGI("WEB", "Joined server network '%s' as %s (http://paravario.local)", g_cfg.serverSsid,
         WiFi.localIP().toString().c_str());
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

void handleStatus() {
  const NavState& n = app.nav;
  JsonDocument doc;
  const char* why = appNotReadyReason();
  doc["ready"] = why == nullptr;
  if (why) doc["notReady"] = why;
  doc["minSats"] = g_cfg.readyMinSats;
  doc["uptime"] = millis() / 1000;
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
#endif
  doc["version"] = PV_VERSION;
  doc["git"] = PV_GIT;
  doc["flying"] = n.flying;
  doc["flightTime"] = n.flightTimeS;
  doc["agl"] = n.aglM;
  doc["altMsl"] = n.altMslM;
  doc["vario"] = n.varioMs;
  doc["muted"] = app.varioMuted;

  // Optional hardware: the page hides what is not fitted, and the sections below are left out.
  JsonObject hwj = doc["hw"].to<JsonObject>();
  hwj["gps"] = hw::hasGps();
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

  doc["mode"] = app.emergency.active() ? "emergency" : app.approach.active() ? "approach" : "none";
  doc["approach"] = app.approach.phaseName();
  if (app.emergency.active()) doc["range"] = app.emergency.lastRangeM();
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["apSsid"] = settings::apSsid();
  wifi["bleName"] = settings::bleName();
  wifi["apIp"] = WiFi.softAPIP().toString();
  wifi["apClients"] = WiFi.softAPgetStationNum();
  wifi["serverSsid"] = serverSsidUsed;
  wifi["server"] = serverWifi == ServerWifi::Connected ? "connected" : serverWifi == ServerWifi::Connecting ? "connecting" : serverWhyOff;
  if (serverWifi == ServerWifi::Connected) {
    wifi["serverIp"] = WiFi.localIP().toString();
    wifi["rssi"] = WiFi.RSSI();
  }
  if (strcmp(serverSsidUsed, g_cfg.serverSsid) != 0 || strcmp(serverPassUsed, g_cfg.serverPass) != 0) {
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
    } else if (k == "learntest") {
      batteryLearnSelfTest();
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

void handleGetSettings() {
  String s;
  settings::toJsonString(s);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

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

void flushPiece() {
  if (!gPieceLen) return;
  char head[kPieceHead + 1];
  snprintf(head, sizeof(head), "%04x\r\n", (unsigned)gPieceLen);
  memcpy(gPiece, head, kPieceHead);
  memcpy(gPiece + kPieceHead + gPieceLen, "\r\n", 2);
  server.client().write((const uint8_t*)gPiece, kPieceHead + gPieceLen + 2);
  gPieceLen = 0;
}

void beginPieces(const char* type) {
  gPieceLen = 0;
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
  server.sendContent("");  // closing zero-length chunk; also ends the server's chunked mode
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
  LOGI("WEB", "settings saved%s", restart ? " (restart needed)" : "");
  if (!settings::save()) {
    g_cfg = previous;
    sendError(500, "save failed");
    return;
  }
  String s;
  settings::toJsonString(s);
  beginPieces("application/json");
  addPiece(restart ? "{\"ok\":true,\"restart\":true,\"settings\":" : "{\"ok\":true,\"restart\":false,\"settings\":");
  addPiece(s);
  addPiece("}", 1);
  endPieces();
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
// ground in quarter metres, oldest first (the last ten minutes, for the flight tab's graph).
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

void handleLogs() {
  JsonDocument doc;
  JsonArray files = doc["files"].to<JsonArray>();
  File dir = fsExists("/logs") ? LittleFS.open("/logs") : File();  // created at first takeoff
  if (dir) {
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      JsonObject o = files.add<JsonObject>();
      o["n"] = f.name();
      o["s"] = f.size();
    }
  }
  doc["free"] = LittleFS.totalBytes() - LittleFS.usedBytes();
  doc["total"] = LittleFS.totalBytes();
  sendJson(doc);
}

void handleLogDownload() {
  String name = server.arg("name");
  if (!validLogName(name)) {
    sendError(400, "bad name");
    return;
  }
  String path = "/logs/" + name;
  File f = fsExists(path) ? LittleFS.open(path, "r") : File();
  if (!f) {
    sendError(404, "not found");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

void handleLogDelete() {
  String name = server.arg("name");
  if (!validLogName(name)) {
    sendError(400, "bad name");
    return;
  }
  if (!LittleFS.remove("/logs/" + name)) {
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
const char* kOtaHeaders[] = {"X-OTA-Password"};

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


// Registers a route and remembers which one is being served, so the memory trace can name it.
const char* gRoute = "";
void route(const char* path, HTTPMethod method, void (*handler)()) {
  server.on(path, method, [path, handler]() {
    gRoute = path;
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

void begin() {
  serverAttemptStartedMs = millis();
  strlcpy(serverSsidUsed, g_cfg.serverSsid, sizeof(serverSsidUsed));
  strlcpy(serverPassUsed, g_cfg.serverPass, sizeof(serverPassUsed));
  bool joinServer = serverSsidUsed[0] != 0;
  serverWhyOff = "not set";
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
  route("/api/status", HTTP_GET, handleStatus);
  route("/api/settings", HTTP_GET, handleGetSettings);
  route("/api/settings", HTTP_POST, handlePostSettings);
  route("/api/schema", HTTP_GET, handleSchema);
  route("/api/action", HTTP_POST, handleAction);
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
  route("/api/log", HTTP_DELETE, handleLogDelete);
  server.on("/api/ota", HTTP_POST, handleOtaDone, handleOtaUpload);
  server.addHandler(new RedirectToUi());
  server.collectHeaders(kOtaHeaders, 1);
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
