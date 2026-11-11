#include "BleFeed.h"

#include <NimBLEDevice.h>

#include "Log.h"
#include "Settings.h"
#include "Hardware.h"
#include "sensors/GpsData.h"

namespace {

const char* kNusService = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
const char* kNusRx = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
const char* kNusTx = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
const char* kDeviceInfoService = "180A";
const char* kManufacturerChar = "2A29";
const char* kModelChar = "2A24";

constexpr uint16_t kDefaultMtu = 23;
constexpr uint16_t kAttOverhead = 3;
constexpr float kAvgTauS = 30.0f;
constexpr int kMaxClients = CONFIG_BT_NIMBLE_MAX_CONNECTIONS;
constexpr uint32_t kAdvCheckMs = 5000;

// Connection parameters asked of the phone: an interval of 30..60 ms keeps the vario sentences
// prompt without crowding the radio time shared with Wi-Fi, and a 4 s supervision timeout rides
// out the short gaps that sharing causes instead of dropping the link. (Units: 1.25 ms, 10 ms.)
constexpr uint16_t kConnIntervalMin = 24, kConnIntervalMax = 48, kConnTimeout = 400;

// Advertising stops whenever a client connects. It is started again at once while there is room
// for another client, and after every disconnect, so the vario can always be found; update()
// also checks every few seconds in case a restart was refused at a busy moment.
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, ble_gap_conn_desc* desc) override {
    LOGI("BLE", "client connected (%d now)", (int)server->getConnectedCount());
    server->updateConnParams(desc->conn_handle, kConnIntervalMin, kConnIntervalMax, 0, kConnTimeout);
    if ((int)server->getConnectedCount() < kMaxClients) NimBLEDevice::startAdvertising();
  }
  void onDisconnect(NimBLEServer*, ble_gap_conn_desc*) override {
    LOGI("BLE", "client disconnected");
    NimBLEDevice::startAdvertising();
  }
};

// Appends "*CS\r\n" to a sentence in buf that starts with '$'. Returns the total length.
size_t finishSentence(char* buf, int bodyLen, size_t size) {
  size_t len = bodyLen < 0 ? 0 : (size_t)bodyLen;
  if (len >= size) len = size - 1;
  uint8_t cs = 0;
  for (size_t i = 1; i < len; ++i) cs ^= (uint8_t)buf[i];
  int n = snprintf(buf + len, size - len, "*%02X\r\n", cs);
  size_t total = len + (n > 0 ? n : 0);
  return total < size ? total : size - 1;
}

void formatCoord(double deg, int degDigits, char pos, char neg, char* out, size_t size, char* hemi) {
  double mag = fabs(deg);
  int d = (int)mag;
  double minutes = (mag - d) * 60.0;
  snprintf(out, size, "%0*d%07.4f", degDigits, d, minutes);
  *hemi = deg < 0 ? neg : pos;
}

}  // namespace

void BleFeed::begin() {
  NimBLEDevice::init(settings::bleName());
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  server_ = NimBLEDevice::createServer();
  server_->setCallbacks(new ServerCallbacks());
  server_->advertiseOnDisconnect(false);  // done in the callback above, with the room check

  NimBLEService* nus = server_->createService(kNusService);
  tx_ = nus->createCharacteristic(kNusTx, NIMBLE_PROPERTY::NOTIFY);
  nus->createCharacteristic(kNusRx, NIMBLE_PROPERTY::WRITE);  // for NUS profile completeness
  nus->start();

  NimBLEService* info = server_->createService(kDeviceInfoService);
  info->createCharacteristic(kManufacturerChar, NIMBLE_PROPERTY::READ)->setValue("ParaVario");
  info->createCharacteristic(kModelChar, NIMBLE_PROPERTY::READ)->setValue("XIAO ESP32C3");
  info->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(kNusService);
  NimBLEAdvertisementData scanResp;
  scanResp.setName(settings::bleName());
  adv->setScanResponseData(scanResp);
  adv->start();
  LOGI("BLE", "advertising as \"%s\"", settings::bleName());
}

int BleFeed::clients() const { return server_ ? server_->getConnectedCount() : 0; }

void BleFeed::update(const NavState& nav, const GpsData& gps, int batteryPct, bool recording) {
  if (nav.ms != lastAvgMs_) {
    float dt = lastAvgMs_ ? (nav.ms - lastAvgMs_) / 1000.0f : 0.0f;
    vario30s_ += (nav.varioMs - vario30s_) * fminf(1.0f, dt / kAvgTauS);
    lastAvgMs_ = nav.ms;
  }
  if (nav.ms - lastAdvCheckMs_ >= kAdvCheckMs) {  // safety net: never stay undiscoverable
    lastAdvCheckMs_ = nav.ms;
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    if (clients() < kMaxClients && adv && !adv->isAdvertising()) {
      LOGW("BLE", "advertising had stopped, restarting");
      adv->start();
    }
  }
  if (clients() == 0) return;

  if (hw::hasGps() && g_cfg.bleGps && nav.ms - lastGpsMs_ >= 1000u / constrain(g_cfg.bleGpsRateHz, 1, 10)) {
    lastGpsMs_ = nav.ms;
    sendGps(nav, gps);
  }
  if (nav.ms - lastVarioMs_ >= 1000u / constrain(g_cfg.bleRateHz, 1, 10)) {
    lastVarioMs_ = nav.ms;
    sendVario(nav, batteryPct, recording);
  }
}

void BleFeed::sendGps(const NavState& nav, const GpsData& gps) {
  char lat[16], lon[16], latH, lonH;
  formatCoord(nav.lat, 2, 'N', 'S', lat, sizeof(lat), &latH);
  formatCoord(nav.lon, 3, 'E', 'W', lon, sizeof(lon), &lonH);

  char timeStr[12] = "000000.000";
  if (gps.timeValid) {
    snprintf(timeStr, sizeof(timeStr), "%02u%02u%02u.%03u", gps.hour, gps.minute, gps.second, gps.centisecond * 10);
  }
  char dateStr[8] = "000000";
  if (gps.dateValid) snprintf(dateStr, sizeof(dateStr), "%02u%02u%02u", gps.day, gps.month, gps.year % 100);

  char buf[100];
  int n = snprintf(buf, sizeof(buf), "$GPGGA,%s,%s,%c,%s,%c,%d,%02u,%.1f,%.1f,M,0.0,M,,", timeStr, lat,
                   latH, lon, lonH, nav.fix ? 1 : 0, nav.sats, nav.hdop < 99 ? nav.hdop : 99.9, nav.altMslM);
  sendBytes(buf, finishSentence(buf, n, sizeof(buf)));

  n = snprintf(buf, sizeof(buf), "$GPRMC,%s,%c,%s,%c,%s,%c,%.1f,%.1f,%s,,,%c", timeStr, nav.fix ? 'A' : 'V',
               lat, latH, lon, lonH, nav.speedMs / 0.514444f, nav.trackDeg, dateStr, nav.fix ? 'A' : 'N');
  sendBytes(buf, finishSentence(buf, n, sizeof(buf)));
}

// $LK8EX1,pressure Pa,altitude m,vario cm/s,temperature C,battery %  (999999 / 99 = no baro)
// $LXWP0,logger Y/N,IAS,baro alt m,vario m/s,vario 30 s avg,,,,,course,,
void BleFeed::sendVario(const NavState& nav, int batteryPct, bool recording) {
  char buf[96];
  int n = snprintf(buf, sizeof(buf), "$LK8EX1,%ld,%.1f,%d,%d,%d", nav.baroOk ? lroundf(nav.pressurePa) : 999999L,
                   nav.altMslM, (int)lroundf(nav.varioMs * 100.0f), nav.baroOk ? (int)lroundf(nav.tempC) : 99,
                   batteryPct);
  sendBytes(buf, finishSentence(buf, n, sizeof(buf)));

  if (!nav.baroOk) return;
  n = snprintf(buf, sizeof(buf), "$LXWP0,%c,,%.1f,%.2f,%.2f,,,,,%.0f,,", recording ? 'Y' : 'N', nav.altMslM,
               nav.varioMs, vario30s_, nav.trackDeg);
  sendBytes(buf, finishSentence(buf, n, sizeof(buf)));
}

// Raw byte stream in default-MTU chunks (smaller if a peer negotiated less); clients
// reassemble sentences on CR/LF like a serial port.
void BleFeed::sendBytes(const char* data, size_t len) {
  if (!tx_ || len == 0) return;
  uint16_t chunk = kDefaultMtu - kAttOverhead;
  for (uint16_t h : server_->getPeerDevices()) {
    uint16_t mtu = server_->getPeerMTU(h);
    if (mtu > kAttOverhead && mtu - kAttOverhead < chunk) chunk = mtu - kAttOverhead;
  }
  for (size_t off = 0; off < len; off += chunk) {
    size_t n = len - off < chunk ? len - off : chunk;
    tx_->setValue(reinterpret_cast<const uint8_t*>(data + off), n);
    tx_->notify();
  }
}
