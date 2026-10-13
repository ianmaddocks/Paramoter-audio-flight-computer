#include "WebUi.h"
#include "Log.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "App.h"
#include "Settings.h"
#include "audio/Speech.h"
#include "comms/WebPage.h"
#include "nav/Geo.h"

namespace webui {
namespace {

WebServer server(80);
DNSServer dns;
bool restartPending = false;
uint32_t restartAtMs = 0;

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
  bool restart = settings::fromJson(in.as<JsonObjectConst>());
  LOGI("WEB", "settings saved%s", restart ? " (restart needed)" : "");
  if (!settings::save()) {
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
  File dir = LittleFS.open("/logs");
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
  File f = LittleFS.open("/logs/" + name, "r");
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

void handleRoot() { server.send_P(200, "text/html", kIndexHtml); }

// Anything else (including OS captive-portal probes) is sent to the UI.
void handleNotFound() {
  server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

}  // namespace

void begin() {
  WiFi.mode(WIFI_AP);
  const char* pass = strlen(g_cfg.wifiPass) >= 8 ? g_cfg.wifiPass : nullptr;
  WiFi.softAP(g_cfg.wifiSsid, pass);
  dns.start(53, "*", WiFi.softAPIP());

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
  server.onNotFound(handleNotFound);
  server.begin();
  LOGI("WEB", "Wi-Fi AP '%s' at %s", g_cfg.wifiSsid, WiFi.softAPIP().toString().c_str());
}

void update() {
  dns.processNextRequest();
  server.handleClient();
  if (restartPending && (int32_t)(millis() - restartAtMs) >= 0) ESP.restart();
}

}  // namespace webui
