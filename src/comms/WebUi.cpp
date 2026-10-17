#include "WebUi.h"
#include "Log.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <sys/stat.h>

#include "App.h"
#include "Settings.h"
#include "audio/Speech.h"
#include "comms/WebPage.h"
#include "nav/Geo.h"

#ifndef PV_VERSION
#define PV_VERSION "unknown"
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
uint32_t serverWindowStartedMs = 0;
int32_t serverWindowUsedMin = 0;
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
  if (app.nav.flying) return serverStop("off while flying");
  if (now - serverWindowStartedMs >= (uint32_t)serverWindowUsedMin * 60000UL) {
    return serverStop("connection window ended");
  }
  bool up = WiFi.status() == WL_CONNECTED;
  if (serverWifi == ServerWifi::Connecting && up) {
    serverWifi = ServerWifi::Connected;
    LOGI("WEB", "Joined server network '%s' as %s (http://paravario.local)", g_cfg.serverSsid,
         WiFi.localIP().toString().c_str());
  } else if (serverWifi == ServerWifi::Connected && !up) {
    serverWifi = ServerWifi::Connecting;
    LOGW("WEB", "Lost server Wi-Fi, retrying within connection window");
  }
}

void sendJson(const JsonDocument& doc, int code = 200) {
  String s;
  serializeJson(doc, s);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", s);
}

void sendError(int code, const char* msg) {
  JsonDocument doc;
  doc["error"] = msg;
  sendJson(doc, code);
}

// First unmet pre-flight condition, or nullptr when ready to fly.
const char* notReadyReason(const NavState& n) {
  if (!app.gps.connected()) return "GPS not responding";
  if (!n.fix || n.sats < g_cfg.readyMinSats) return "Waiting for satellites";
  if (n.hdop > g_cfg.readyMaxHdop) return "GPS accuracy low";
  if (!n.baroOk) return "Barometer not working";
  if (!app.baro.settled()) return "Vario settling";
  if (!n.lzValid) return "Landing zone not set";
  return nullptr;
}

void handleStatus() {
  const NavState& n = app.nav;
  JsonDocument doc;
  const char* why = notReadyReason(n);
  doc["ready"] = why == nullptr;
  if (why) doc["notReady"] = why;
  doc["minSats"] = g_cfg.readyMinSats;
  doc["uptime"] = millis() / 1000;
  doc["heap"] = ESP.getFreeHeap();
  doc["debug"] = dlog::kDebugBuild;
  doc["version"] = PV_VERSION;
  doc["flying"] = n.flying;
  doc["flightTime"] = n.flightTimeS;
  doc["agl"] = n.aglM;
  doc["altMsl"] = n.altMslM;
  doc["vario"] = n.varioMs;
  doc["muted"] = app.varioMuted;

  JsonObject gps = doc["gps"].to<JsonObject>();
  gps["link"] = app.gps.connected();
  gps["fix"] = n.fix;
  gps["sats"] = n.sats;
  gps["hdop"] = n.hdop;
  gps["lat"] = n.lat;
  gps["lon"] = n.lon;
  gps["speed"] = n.speedMs;
  gps["track"] = n.trackDeg;
  gps["alt"] = n.gpsAltM;

  JsonObject baro = doc["baro"].to<JsonObject>();
  baro["ok"] = n.baroOk;
  baro["pressure"] = n.pressurePa;
  baro["temp"] = n.tempC;

  JsonObject batt = doc["batt"].to<JsonObject>();
  batt["v"] = app.battery.volts();
  batt["pct"] = app.battery.percent();

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
  wifi["apIp"] = WiFi.softAPIP().toString();
  wifi["apClients"] = WiFi.softAPgetStationNum();
  wifi["serverSsid"] = serverSsidUsed;
  wifi["server"] = serverWifi == ServerWifi::Connected ? "connected" : serverWifi == ServerWifi::Connecting ? "connecting" : serverWhyOff;
  if (serverWifi == ServerWifi::Connected) {
    wifi["serverIp"] = WiFi.localIP().toString();
    wifi["rssi"] = WiFi.RSSI();
  }
  if (strcmp(serverSsidUsed, g_cfg.serverSsid) != 0 || strcmp(serverPassUsed, g_cfg.serverPass) != 0 ||
      serverWindowUsedMin != g_cfg.serverConnectWindowMin) {
    wifi["restart"] = true;
  }
  doc["ble"] = app.ble.clients();
  doc["logging"] = app.logger.active();
  sendJson(doc);
}

void handleGetSettings() {
  JsonDocument doc;
  settings::toJson(doc.to<JsonObject>());
  sendJson(doc);
}

void handleSchema() {
  JsonDocument doc;
  settings::schemaJson(doc.to<JsonArray>());
  sendJson(doc);
}

void handlePostSettings() {
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain")) != DeserializationError::Ok || !in.is<JsonObject>()) {
    sendError(400, "invalid JSON");
    return;
  }
  Settings previous = g_cfg;
  bool restart = settings::fromJson(in.as<JsonObjectConst>());
  LOGI("WEB", "settings saved%s", restart ? " (restart needed)" : "");
  if (!settings::save()) {
    g_cfg = previous;
    sendError(500, "save failed");
    return;
  }
  JsonDocument out;
  out["ok"] = true;
  out["restart"] = restart;
  settings::toJson(out["settings"].to<JsonObject>());
  sendJson(out);
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
    if (!n.fix) {
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
  String lines;
  uint32_t dropped = 0;
  uint32_t next = dlog::read(since, lines, dropped);
  String body = "#next=" + String(next) + " dropped=" + String(dropped) +
                " debug=" + String(dlog::kDebugBuild ? 1 : 0) + "\n";
  body += lines;
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/plain", body);
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
// LittleFS image, optional ?md5=). Refused in flight; the vario restarts after a good image.
String otaError;
bool otaFs = false;

void otaFail(const String& msg) {
  if (otaError.isEmpty()) otaError = msg;
  if (Update.isRunning()) Update.abort();
}

void handleOtaUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaError = "";
    otaFs = server.arg("target") == "fs";
    if (app.nav.flying) {
      otaError = "not allowed in flight";
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
    if (otaError.isEmpty()) LOGI("OTA", "Update OK, %u bytes", (unsigned)up.totalSize);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    otaFail("upload aborted");
  }
}

void handleOtaDone() {
  if (!otaError.isEmpty() || !Update.isFinished()) {
    if (otaError.isEmpty()) otaError = "no image received";
    LOGE("OTA", "Update failed: %s", otaError.c_str());
    sendError(otaError == "not allowed in flight" ? 409 : 500, otaError.c_str());
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

void begin() {
  serverWindowStartedMs = millis();
  serverWindowUsedMin = g_cfg.serverConnectWindowMin;
  strlcpy(serverSsidUsed, g_cfg.serverSsid, sizeof(serverSsidUsed));
  strlcpy(serverPassUsed, g_cfg.serverPass, sizeof(serverPassUsed));
  bool joinServer = serverSsidUsed[0] != 0 && serverWindowUsedMin > 0;
  serverWhyOff = serverWindowUsedMin <= 0 ? "connection window disabled" : "not set";
  WiFi.setHostname("paravario");
  WiFi.mode(joinServer ? WIFI_AP_STA : WIFI_AP);
  const char* pass = strlen(g_cfg.wifiPass) >= 8 ? g_cfg.wifiPass : nullptr;
  WiFi.softAP(g_cfg.wifiSsid, pass);
  dns.start(53, "*", WiFi.softAPIP());
  if (joinServer) {
    WiFi.begin(g_cfg.serverSsid, g_cfg.serverPass);
    serverWifi = ServerWifi::Connecting;
    LOGI("WEB", "Joining server network '%s' for up to %ld min", g_cfg.serverSsid,
         (long)serverWindowUsedMin);
  }
  if (MDNS.begin("paravario")) MDNS.addService("http", "tcp", 80);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/settings", HTTP_POST, handlePostSettings);
  server.on("/api/schema", HTTP_GET, handleSchema);
  server.on("/api/action", HTTP_POST, handleAction);
  server.on("/api/logs", HTTP_GET, handleLogs);
  server.on("/api/debuglog", HTTP_GET, handleDebugLog);
  server.on("/api/log", HTTP_GET, handleLogDownload);
  server.on("/api/log", HTTP_DELETE, handleLogDelete);
  server.on("/api/ota", HTTP_POST, handleOtaDone, handleOtaUpload);
  server.addHandler(new RedirectToUi());
  server.begin();
  LOGI("WEB", "Wi-Fi AP '%s' at %s", g_cfg.wifiSsid, WiFi.softAPIP().toString().c_str());
}

void update() {
  dns.processNextRequest();
  server.handleClient();
  serverUpdate(millis());
  if (restartPending && (int32_t)(millis() - restartAtMs) >= 0) ESP.restart();
}

}  // namespace webui
