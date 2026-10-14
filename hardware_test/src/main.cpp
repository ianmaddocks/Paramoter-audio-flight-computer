// ParaVario hardware test: exercises each peripheral and reports via web page + USB serial.
#include <Adafruit_DPS310.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <NimBLEDevice.h>
#include <TinyGPSPlus.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

#include "../../Paramoter audio flight computer/src/Pins.h"
#include "TestAudio.h"
#include "TestPage.h"

namespace {

const char* kSsid = "ParaVario";
const char* kPass = "";
const char* kBleName = "ParaVario-Test";

WebServer server(80);
DNSServer dns;

// ---------------------------------------------------------------- I2C
uint8_t i2cFound[32];
int i2cCount = 0;

void scanI2c() {
  i2cCount = 0;
  for (uint8_t a = 1; a < 127 && i2cCount < 32; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) i2cFound[i2cCount++] = a;
  }
  Serial.printf("[i2c] %d device(s):", i2cCount);
  for (int i = 0; i < i2cCount; ++i) Serial.printf(" 0x%02X", i2cFound[i]);
  Serial.println();
}

bool i2cHas(uint8_t addr) {
  for (int i = 0; i < i2cCount; ++i)
    if (i2cFound[i] == addr) return true;
  return false;
}

// ---------------------------------------------------------------- Barometer
Adafruit_DPS310 dps;
bool baroPresent = false;
uint32_t baroSamples = 0, baroRateCount = 0, baroRateMs = 0, lastBaroMs = 0;
float baroRate = 0, pressurePa = 0, tempC = 0, altM = 0, altFilt = 0, varioMs = 0;

void initBaro() {
  baroPresent = dps.begin_I2C(DPS310_ADDR, &Wire);
  if (baroPresent) {
    dps.configurePressure(DPS310_32HZ, DPS310_8SAMPLES);
    dps.configureTemperature(DPS310_2HZ, DPS310_2SAMPLES);
    dps.setMode(DPS310_CONT_PRESTEMP);
  }
  Serial.printf("[baro] DPS310 at 0x%02X: %s\n", DPS310_ADDR, baroPresent ? "found" : "NOT FOUND");
}

void updateBaro(uint32_t now) {
  if (!baroPresent || !dps.pressureAvailable()) return;
  sensors_event_t t, p;
  if (!dps.getEvents(&t, &p)) return;
  float dt = lastBaroMs ? (now - lastBaroMs) / 1000.0f : 0;
  lastBaroMs = now;
  pressurePa = p.pressure * 100.0f;
  tempC = t.temperature;
  altM = 44330.8f * (1.0f - powf(pressurePa / 101325.0f, 0.190263f));
  if (baroSamples == 0) {
    altFilt = altM;
  } else if (dt > 0) {
    float prev = altFilt;
    altFilt += (altM - altFilt) * 0.15f;
    varioMs += ((altFilt - prev) / dt - varioMs) * 0.1f;
  }
  ++baroSamples;
  ++baroRateCount;
  if (now - baroRateMs >= 1000) {
    baroRate = baroRateCount * 1000.0f / (now - baroRateMs);
    baroRateCount = 0;
    baroRateMs = now;
  }
}

// ---------------------------------------------------------------- GPS
HardwareSerial& gpsSer = Serial1;
TinyGPSPlus gps;
const uint32_t kBauds[] = {9600, 38400, 57600, 115200, 230400};
constexpr int kNumBauds = sizeof(kBauds) / sizeof(kBauds[0]);
int baudIdx = 0;
bool baudLocked = false;
uint32_t listenStartMs = 0, chkAtListen = 0, lastGpsByteMs = 0, gpsBytes = 0;
constexpr int kRawLines = 8;
char rawLines[kRawLines][90];
int rawHead = 0;
char curLine[90];
int curLen = 0;

void openGps(uint32_t baud) {
  gpsSer.end();
  gpsSer.setRxBufferSize(1024);
  gpsSer.begin(baud, SERIAL_8N1, pins::GPS_RX, pins::GPS_TX);
  listenStartMs = millis();
  chkAtListen = gps.passedChecksum();
}

void updateGps(uint32_t now) {
  while (gpsSer.available()) {
    char c = gpsSer.read();
    gps.encode(c);
    ++gpsBytes;
    lastGpsByteMs = now;
    if (c == '$') curLen = 0;
    if (c == '\r') continue;
    if (c == '\n') {
      curLine[curLen] = 0;
      if (curLen > 5) strlcpy(rawLines[rawHead++ % kRawLines], curLine, sizeof(rawLines[0]));
      curLen = 0;
    } else if (curLen < (int)sizeof(curLine) - 1 && c >= 32 && c < 127) {
      curLine[curLen++] = c;
    }
  }
  if (baudLocked) {
    if (now - lastGpsByteMs > 5000) {
      baudLocked = false;
      Serial.println("[gps] data stopped, re-detecting baud");
      openGps(kBauds[baudIdx = 0]);
    }
    return;
  }
  if (gps.passedChecksum() - chkAtListen >= 3) {
    baudLocked = true;
    Serial.printf("[gps] valid NMEA at %lu baud\n", (unsigned long)kBauds[baudIdx]);
  } else if (now - listenStartMs > 2000) {
    baudIdx = (baudIdx + 1) % kNumBauds;
    openGps(kBauds[baudIdx]);
  }
}

bool gpsFix() {
  return gps.location.isValid() && gps.location.age() < 3000 && gps.satellites.value() >= 4;
}

// ---------------------------------------------------------------- Battery
float battMv = 0, battV = 0;
uint32_t lastBattMs = 0;

void updateBattery(uint32_t now) {
  if (lastBattMs && now - lastBattMs < 500) return;
  lastBattMs = now;
  uint32_t sum = 0;
  for (int i = 0; i < 16; ++i) sum += analogReadMilliVolts(pins::BATT_ADC);
  battMv = sum / 16.0f;
  battV = battMv * 2.0f / 1000.0f;
}

// ---------------------------------------------------------------- Button
bool btnRaw = false, btnDown = false, btnLong = false, beepOnPress = true;
uint32_t btnChangeMs = 0, btnPressMs = 0, btnReleaseMs = 0, btnPresses = 0, btnLastMs = 0;
uint8_t btnClicks = 0;
const char* btnEvent = "none";

void updateButton(uint32_t now) {
  bool raw = digitalRead(pins::BUTTON) == LOW;
  if (raw != btnRaw) {
    btnRaw = raw;
    btnChangeMs = now;
  }
  if (raw != btnDown && now - btnChangeMs >= 25) {
    btnDown = raw;
    if (btnDown) {
      btnPressMs = now;
      btnLong = false;
      ++btnPresses;
      if (beepOnPress && !testaudio::busy()) testaudio::tone(1000, 60, testaudio::Wave::Sine);
    } else {
      btnLastMs = now - btnPressMs;
      Serial.printf("[button] released after %lu ms\n", (unsigned long)btnLastMs);
      if (!btnLong) {
        ++btnClicks;
        btnReleaseMs = now;
        if (btnClicks >= 2) {
          btnClicks = 0;
          btnEvent = "double";
          Serial.println("[button] double press");
        }
      }
    }
  }
  if (btnDown && !btnLong && now - btnPressMs >= 800) {
    btnLong = true;
    btnClicks = 0;
    btnEvent = "long";
    Serial.println("[button] long press");
  }
  if (!btnDown && btnClicks == 1 && now - btnReleaseMs >= 400) {
    btnClicks = 0;
    btnEvent = "single";
    Serial.println("[button] single press");
  }
}

// ---------------------------------------------------------------- Bluetooth
NimBLEServer* bleServer = nullptr;
NimBLECharacteristic* bleTx = nullptr;
uint32_t bleSent = 0, lastBleMs = 0;

void initBle() {
  NimBLEDevice::init(kBleName);
  bleServer = NimBLEDevice::createServer();
  NimBLEService* nus = bleServer->createService("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
  bleTx = nus->createCharacteristic("6E400003-B5A3-F393-E0A9-E50E24DCCA9E", NIMBLE_PROPERTY::NOTIFY);
  nus->createCharacteristic("6E400002-B5A3-F393-E0A9-E50E24DCCA9E", NIMBLE_PROPERTY::WRITE);
  nus->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
  NimBLEAdvertisementData sr;
  sr.setName(kBleName);
  adv->setScanResponseData(sr);
  adv->start();
  Serial.printf("[ble] advertising as %s\n", kBleName);
}

void updateBle(uint32_t now) {
  if (now - lastBleMs < 1000 || bleServer->getConnectedCount() == 0) return;
  lastBleMs = now;
  char body[64], line[80];
  snprintf(body, sizeof(body), "LK8EX1,%ld,%.1f,%d,%d,%d", baroPresent ? lroundf(pressurePa) : 999999L, altM,
           (int)lroundf(varioMs * 100), baroPresent ? (int)lroundf(tempC) : 99, 100);
  uint8_t cs = 0;
  for (const char* p = body; *p; ++p) cs ^= (uint8_t)*p;
  int n = snprintf(line, sizeof(line), "$%s*%02X\r\n", body, cs);
  for (int off = 0; off < n; off += 20) {
    bleTx->setValue(reinterpret_cast<uint8_t*>(line + off), min(20, n - off));
    bleTx->notify();
  }
  ++bleSent;
}

// ---------------------------------------------------------------- Storage
bool fsMounted = false;
int clipCount = 0;

void scanStorage() {
  fsMounted = LittleFS.begin(false);  // never format: keep the main firmware's voice clips
  clipCount = 0;
  if (fsMounted) {
    File dir = LittleFS.open("/v");
    if (dir)
      for (File f = dir.openNextFile(); f; f = dir.openNextFile()) clipCount++;
  }
  Serial.printf("[storage] LittleFS %s, %d voice clips\n", fsMounted ? "mounted" : "NOT mounted", clipCount);
}

// ---------------------------------------------------------------- Web
void addCheck(JsonObject o, const char* state, const String& msg) {
  o["state"] = state;
  o["msg"] = msg;
}

void handleStatus() {
  const uint32_t now = millis();
  JsonDocument doc;
  doc["uptime"] = now / 1000;
  doc["heap"] = ESP.getFreeHeap();
  doc["reset"] = (int)esp_reset_reason();
  doc["chip"] = ESP.getChipModel();
  doc["rev"] = ESP.getChipRevision();
  doc["flash"] = ESP.getFlashChipSize();
  doc["sdk"] = ESP.getSdkVersion();

  JsonObject st = doc["storage"].to<JsonObject>();
  st["mounted"] = fsMounted;
  st["clips"] = clipCount;
  if (fsMounted) {
    st["used"] = LittleFS.usedBytes();
    st["total"] = LittleFS.totalBytes();
  }
  if (!fsMounted) addCheck(st, "fail", "LittleFS not mounted. Run uploadfs from the main project");
  else if (!clipCount) addCheck(st, "warn", "No voice clips. Run uploadfs from the main project");
  else addCheck(st, "ok", String(clipCount) + " voice clips");

  JsonObject i2c = doc["i2c"].to<JsonObject>();
  JsonArray addrs = i2c["found"].to<JsonArray>();
  for (int i = 0; i < i2cCount; ++i) {
    char a[6];
    snprintf(a, sizeof(a), "0x%02X", i2cFound[i]);
    addrs.add(a);
  }
  if (i2cHas(DPS310_ADDR)) addCheck(i2c, "ok", "DPS310 answers at 0x77");
  else if (i2cHas(0x76)) addCheck(i2c, "fail", "Device at 0x76, firmware expects 0x77 (SDO pin)");
  else if (i2cCount) addCheck(i2c, "fail", "No device at 0x77");
  else addCheck(i2c, "fail", "No I2C devices. Check SDA=D8, SCL=D7, 3V3, GND");

  JsonObject b = doc["baro"].to<JsonObject>();
  b["present"] = baroPresent;
  b["samples"] = baroSamples;
  b["rate"] = baroRate;
  b["pressure"] = pressurePa;
  b["temp"] = tempC;
  b["alt"] = altM;
  b["vario"] = varioMs;
  bool baroFresh = baroPresent && lastBaroMs && now - lastBaroMs < 2000;
  if (!baroPresent) addCheck(b, "fail", "DPS310 did not initialise");
  else if (!baroFresh) addCheck(b, "fail", "No readings");
  else if (pressurePa < 30000 || pressurePa > 110000) addCheck(b, "warn", "Pressure out of range");
  else addCheck(b, "ok", String(baroRate, 0) + " readings/s. Lift it ~1 m and watch vario");

  JsonObject g = doc["gps"].to<JsonObject>();
  g["bytes"] = gpsBytes;
  g["baud"] = kBauds[baudIdx];
  g["locked"] = baudLocked;
  g["passed"] = gps.passedChecksum();
  g["failed"] = gps.failedChecksum();
  g["fix"] = gpsFix();
  g["sats"] = gps.satellites.value();
  g["hdop"] = gps.hdop.isValid() ? gps.hdop.hdop() : 99.0;
  g["lat"] = gps.location.lat();
  g["lon"] = gps.location.lng();
  g["alt"] = gps.altitude.meters();
  g["speed"] = gps.speed.kmph();
  g["course"] = gps.course.deg();
  if (gps.time.isValid()) {
    char t[12];
    snprintf(t, sizeof(t), "%02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
    g["utc"] = t;
  }
  JsonArray raw = g["raw"].to<JsonArray>();
  for (int i = 0; i < kRawLines; ++i) {
    const char* l = rawLines[(rawHead + i) % kRawLines];
    if (*l) raw.add(l);
  }
  if (!gpsBytes) addCheck(g, "fail", "No data on D9. Check GPS TX to D9, power");
  else if (!baudLocked) addCheck(g, "warn", "Data received but no valid NMEA yet (finding baud)");
  else if (!gpsFix()) addCheck(g, "warn", String("NMEA OK at ") + kBauds[baudIdx] + " baud, no fix yet (" +
                                            gps.satellites.value() + " sats). Go outside");
  else addCheck(g, "ok", String("Fix, ") + gps.satellites.value() + " sats");

  JsonObject bt = doc["battery"].to<JsonObject>();
  bt["mv"] = battMv;
  bt["v"] = battV;
  if (battV < 2.5f) addCheck(bt, "warn", "No battery voltage. On USB, battery not connected, or divider fault");
  else if (battV < 3.0f || battV > 4.35f) addCheck(bt, "warn", "Voltage outside LiPo range");
  else addCheck(bt, "ok", String(battV, 2) + " V");

  JsonObject bn = doc["button"].to<JsonObject>();
  bn["down"] = btnDown;
  bn["presses"] = btnPresses;
  bn["event"] = btnEvent;
  bn["lastMs"] = btnLastMs;
  bn["beep"] = beepOnPress;
  if (btnPresses) addCheck(bn, "ok", String(btnPresses) + " presses detected");
  else addCheck(bn, "warn", "Press the button to test");

  JsonObject au = doc["audio"].to<JsonObject>();
  au["mode"] = testaudio::mode();
  au["busy"] = testaudio::busy();
  au["volume"] = testaudio::volume();
  au["error"] = testaudio::lastError();
  if (!testaudio::ok()) addCheck(au, "fail", testaudio::lastError());
  else if (*testaudio::lastError()) addCheck(au, "warn", testaudio::lastError());
  else addCheck(au, "ok", "I2S running. Use the tests below and listen");

  JsonObject bl = doc["ble"].to<JsonObject>();
  bl["clients"] = bleServer->getConnectedCount();
  bl["sent"] = bleSent;
  addCheck(bl, "ok", bleServer->getConnectedCount() ? String("Connected, ") + bleSent + " test sentences sent"
                                                    : String("Advertising as ") + kBleName);

  JsonObject wf = doc["wifi"].to<JsonObject>();
  wf["clients"] = WiFi.softAPgetStationNum();
  addCheck(wf, "ok", String(WiFi.softAPgetStationNum()) + " client(s) on " + kSsid);

  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

void handleAudio() {
  String what = server.arg("do");
  float a = server.arg("a").toFloat(), b = server.arg("b").toFloat();
  uint32_t ms = server.arg("ms").toInt();
  if (what == "tone") testaudio::tone(a, ms, server.arg("wave") == "square" ? testaudio::Wave::Square : testaudio::Wave::Sine);
  else if (what == "sweep") testaudio::sweep(a, b, ms);
  else if (what == "vario") testaudio::vario(a);
  else if (what == "clip") testaudio::clip(server.arg("name").c_str());
  else if (what == "stop") testaudio::stop();
  else if (what == "volume") testaudio::setVolume((uint8_t)a);
  else if (what == "beep") beepOnPress = a != 0;
  else {
    server.send(400, "text/plain", "unknown audio action");
    return;
  }
  Serial.printf("[web] audio %s a=%.1f b=%.1f ms=%lu %s\n", what.c_str(), a, b, (unsigned long)ms,
                server.arg("name").c_str());
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleClips() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  File dir = LittleFS.open("/v");
  if (dir) {
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      String n = f.name();
      if (n.endsWith(".adp")) arr.add(n.substring(0, n.length() - 4));
    }
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleAction() {
  String what = server.arg("do");
  if (what == "i2c") {
    scanI2c();
    if (!baroPresent && i2cHas(DPS310_ADDR)) initBaro();
  } else if (what == "restart") {
    server.send(200, "application/json", "{\"ok\":true}");
    delay(300);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "unknown action");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void initWeb() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(kSsid, kPass);
  dns.start(53, "*", WiFi.softAPIP());
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", kTestPage); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/audio", HTTP_POST, handleAudio);
  server.on("/api/clips", HTTP_GET, handleClips);
  server.on("/api/action", HTTP_POST, handleAction);
  server.onNotFound([] {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();
  Serial.printf("[wifi] AP \"%s\" password \"%s\" at http://%s\n", kSsid, kPass,
                WiFi.softAPIP().toString().c_str());
}

void printSummary() {
  Serial.printf("[summary] baro %s p=%.0fPa t=%.1fC vs=%+.2f | gps bytes=%lu baud=%lu %s sats=%u | "
                "batt %.2fV | button presses=%lu | ble clients=%d | wifi clients=%d | heap %u\n",
                baroPresent ? "ok" : "MISSING", pressurePa, tempC, varioMs, (unsigned long)gpsBytes,
                (unsigned long)kBauds[baudIdx], gpsFix() ? "FIX" : "nofix", gps.satellites.value(), battV,
                (unsigned long)btnPresses, bleServer->getConnectedCount(), WiFi.softAPgetStationNum(),
                ESP.getFreeHeap());
}

uint32_t lastSummaryMs = 0;

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  delay(1500);  // time to open the serial monitor
  Serial.printf("\n=== ParaVario hardware test === reset reason %d\n", (int)esp_reset_reason());

  pinMode(pins::BUTTON, INPUT_PULLUP);
  analogSetPinAttenuation(pins::BATT_ADC, ADC_11db);
  scanStorage();
  if (testaudio::begin()) Serial.println("[audio] I2S started");

  Wire.begin(pins::I2C_SDA, pins::I2C_SCL, 400000);
  scanI2c();
  initBaro();
  openGps(kBauds[0]);
  initBle();
  initWeb();

  testaudio::sweep(400, 1600, 600);  // power-on chirp: speaker and amp work
}

void loop() {
  const uint32_t now = millis();
  updateBaro(now);
  updateGps(now);
  updateBattery(now);
  updateButton(now);
  updateBle(now);
  dns.processNextRequest();
  server.handleClient();
  if (now - lastSummaryMs >= 5000) {
    lastSummaryMs = now;
    printSummary();
  }
  delay(1);
}
