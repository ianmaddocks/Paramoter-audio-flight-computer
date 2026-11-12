#include "Gps.h"

#include <Preferences.h>

#include "Log.h"

#include "Pins.h"
#include "Settings.h"
#include "nav/Geo.h"
#include "test/Sim.h"

namespace {
constexpr uint32_t kFixGapMs = 2000;    // longer than this between fixes counts as re-acquisition
constexpr uint32_t kSteadyMs = 3000;    // fix must be jump-free this long to be trusted
constexpr float kJumpSlackM = 12.0f;    // position change allowed beyond what the reported speed explains
constexpr float kMaxAccelMs2 = 8.0f;    // reported speed cannot change faster than this
constexpr float kSpeedSlackMs = 1.0f;
constexpr uint32_t kTargetBaud = 115200;
constexpr uint32_t kBauds[] = {115200, 9600, 38400, 57600};
constexpr uint32_t kListenMs = 1500;
// A GPS module sends sentences from power-on (the baud rates are tried in turn, about 6 s for all
// four), so a vario that has never seen one calls it absent if none have arrived by then.
constexpr uint32_t kDetectWindowMs = 10000;
constexpr int kSentencesToTrust = 3;  // valid sentences that count as "a GPS is there"

hw::GpsState gState = hw::GpsState::Unknown;  // what update() last concluded; read through hw::gpsState()

// UBX-CFG-VALSET configuration keys (u-blox M10 interface description).
constexpr uint32_t CFG_UART1_BAUDRATE = 0x40520001;
constexpr uint32_t CFG_RATE_MEAS = 0x30210001;
constexpr uint32_t CFG_RATE_NAV = 0x30210002;
constexpr uint32_t CFG_NAVSPG_DYNMODEL = 0x20110021;
constexpr uint32_t CFG_MSGOUT_GGA_UART1 = 0x209100bb;
constexpr uint32_t CFG_MSGOUT_RMC_UART1 = 0x209100ac;
constexpr uint32_t CFG_MSGOUT_GSA_UART1 = 0x209100c0;
constexpr uint32_t CFG_MSGOUT_GSV_UART1 = 0x209100c5;
constexpr uint32_t CFG_MSGOUT_GLL_UART1 = 0x209100ca;
constexpr uint32_t CFG_MSGOUT_VTG_UART1 = 0x209100b1;
constexpr uint32_t CFG_TP_TP1_ENA = 0x10050007;  // time-pulse output (drives the LED on most breakouts)
constexpr uint8_t DYNMODEL_AIRBORNE_1G = 6;

uint8_t keySize(uint32_t key) {
  static const uint8_t kSizes[8] = {0, 1, 1, 2, 4, 8, 0, 0};
  return kSizes[(key >> 28) & 0x7];
}
}  // namespace

namespace hw {
GpsState gpsState() {
#if PV_TEST
  if (!sim::gpsFitted()) return GpsState::Absent;  // test build: behave as a vario with no GPS fitted
#endif
  return gState;
}
}  // namespace hw

void Gps::begin() {
  Preferences prefs;
  if (prefs.begin("gps", true)) {  // has a GPS ever been seen on this unit?
    seenEver_ = prefs.getBool("seen", false);
    prefs.end();
  }
  startMs_ = millis();
  if (g_cfg.gpsMode != GPS_NOT_FITTED) start();
}

void Gps::start() {
  ser_.setRxBufferSize(1024);
  baudIdx_ = 0;
  baud_ = 0;
  openBaud(kBauds[0]);
  active_ = true;
}

void Gps::stop() {
  ser_.end();
  active_ = false;
  baud_ = 0;
  locked_ = false;
}

#if PV_TEST
void Gps::forgetForTest() {
  Preferences prefs;
  if (prefs.begin("gps", false)) {
    prefs.remove("seen");
    prefs.end();
  }
  seen_ = seenEver_ = false;
  gps_ = TinyGPSPlus();
  checksumAtListen_ = 0;
  locked_ = false;
  startMs_ = millis();
  gState = hw::GpsState::Unknown;
}
#endif

void Gps::openBaud(uint32_t baud) {
  if (baud_) ser_.end();
  ser_.begin(baud, SERIAL_8N1, pins::GPS_RX, pins::GPS_TX);
  baud_ = baud;
  LOGD("GPS", "listening at %lu baud", (unsigned long)baud);
  listenStartMs_ = millis();
  checksumAtListen_ = gps_.passedChecksum();
}

void Gps::valset(std::initializer_list<KV> kvs) {
  uint8_t payload[64];
  size_t len = 0;
  payload[len++] = 0;     // version
  payload[len++] = 0x01;  // RAM layer only: reapplied on every boot
  payload[len++] = 0;
  payload[len++] = 0;
  for (const KV& kv : kvs) {
    uint8_t n = keySize(kv.key);
    if (len + 4 + n > sizeof(payload)) break;
    for (int i = 0; i < 4; ++i) payload[len++] = (kv.key >> (8 * i)) & 0xFF;
    for (int i = 0; i < n; ++i) payload[len++] = (kv.value >> (8 * i)) & 0xFF;
  }
  uint8_t hdr[6] = {0xB5, 0x62, 0x06, 0x8A, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8)};
  uint8_t ckA = 0, ckB = 0;
  for (int i = 2; i < 6; ++i) {
    ckA += hdr[i];
    ckB += ckA;
  }
  for (size_t i = 0; i < len; ++i) {
    ckA += payload[i];
    ckB += ckA;
  }
  ser_.write(hdr, sizeof(hdr));
  ser_.write(payload, len);
  ser_.write(ckA);
  ser_.write(ckB);
  ser_.flush();
  LOGD("GPS", "sent UBX-CFG-VALSET with %u keys", (unsigned)kvs.size());
}

void Gps::sendConfig() {
  uint16_t measMs = 1000 / constrain(g_cfg.gpsRateHz, 1, 10);
  valset({{CFG_RATE_MEAS, measMs},
          {CFG_RATE_NAV, 1},
          {CFG_NAVSPG_DYNMODEL, DYNMODEL_AIRBORNE_1G}});
  delay(20);
  valset({{CFG_MSGOUT_GGA_UART1, 1},
          {CFG_MSGOUT_RMC_UART1, 1},
          {CFG_MSGOUT_GSA_UART1, 0},
          {CFG_MSGOUT_GSV_UART1, 0},
          {CFG_MSGOUT_GLL_UART1, 0},
          {CFG_MSGOUT_VTG_UART1, 0}});
  delay(20);
  // Its own message: the receiver rejects a whole VALSET if it dislikes one key, and this one
  // is cosmetic, so it must not take the settings above down with it.
  valset({{CFG_TP_TP1_ENA, g_cfg.gpsLed ? 1u : 0u}});
}

void Gps::update(uint32_t nowMs) {
#if PV_TEST
  if (sim::takeGpsForget()) forgetForTest();
#endif
  if (g_cfg.gpsMode == GPS_NOT_FITTED) {  // set to "Not fitted": leave the port alone
    if (active_) stop();
    gState = hw::GpsState::Absent;
    return;
  }
  if (!active_) start();
  pollLink(nowMs);
  updatePresence(nowMs);
}

// Decides whether a GPS module is there: present once valid sentences have arrived (and remembered
// from then on), absent if a vario that has never seen one hears nothing for kDetectWindowMs.
void Gps::updatePresence(uint32_t nowMs) {
  if (!seen_ && gps_.passedChecksum() >= (uint32_t)kSentencesToTrust) {
    seen_ = true;
    if (!seenEver_) {
      seenEver_ = true;
      Preferences prefs;
      if (prefs.begin("gps", false)) {
        prefs.putBool("seen", true);
        prefs.end();
      }
    }
  }
  hw::GpsState s;
  if (g_cfg.gpsMode == GPS_ALWAYS || seen_ || seenEver_) s = hw::GpsState::Present;
  else s = nowMs - startMs_ >= kDetectWindowMs ? hw::GpsState::Absent : hw::GpsState::Unknown;
  if (s != gState) {
    gState = s;
    LOGI("GPS", "GPS module: %s", s == hw::GpsState::Present ? "present" : s == hw::GpsState::Absent ? "not fitted" : "looking");
  }
}

void Gps::pollLink(uint32_t nowMs) {
  if (sim::gpsMode() != sim::GpsMode::Real) {  // test build only: simulated NMEA, same parser
    char buf[200];
    size_t n = sim::gpsNmea(nowMs, buf, sizeof(buf));
    for (size_t i = 0; i < n; ++i) gps_.encode(buf[i]);
    while (ser_.available()) ser_.read();
    locked_ = sim::gpsMode() != sim::GpsMode::Silent;
    if (locked_) lastCharMs_ = nowMs;
    if (gps_.speed.isUpdated()) screenFix(nowMs);
    return;
  }
  while (ser_.available()) {
    char c = ser_.read();
    gps_.encode(c);
    lastCharMs_ = nowMs;
  }
  if (gps_.speed.isUpdated()) screenFix(nowMs);  // once per epoch: RMC carries position and speed

  if (locked_) {
    if (nowMs - lastCharMs_ > 5000) {
      LOGW("GPS", "GPS silent, re-detecting baud rate");
      locked_ = false;
      baudIdx_ = 0;
      openBaud(kBauds[0]);
    }
    return;
  }

  if (gps_.passedChecksum() - checksumAtListen_ >= 3) {
    LOGI("GPS", "GPS found at %lu baud", (unsigned long)baud_);
    if (baud_ != kTargetBaud) {
      valset({{CFG_UART1_BAUDRATE, kTargetBaud}});
      delay(100);
      openBaud(kTargetBaud);
    }
    sendConfig();
    locked_ = true;
    lastCharMs_ = nowMs;
    return;
  }
  if (nowMs - listenStartMs_ > kListenMs) {
    baudIdx_ = (baudIdx_ + 1) % (sizeof(kBauds) / sizeof(kBauds[0]));
    openBaud(kBauds[baudIdx_]);
  }
}

void Gps::screenFix(uint32_t nowMs) {
  double lat = gps_.location.lat();
  double lon = gps_.location.lng();
  float speed = gps_.speed.mps();
  if (!havePrev_ || nowMs - prevFixMs_ > kFixGapMs) {
    suspectMs_ = nowMs;
  } else {
    float dt = (nowMs - prevFixMs_) / 1000.0f;
    float dist = (float)geo::distanceM(prevLat_, prevLon_, lat, lon);
    bool jump = dist > fmaxf(speed, prevSpeed_) * dt + kJumpSlackM;
    bool surge = fabsf(speed - prevSpeed_) > kMaxAccelMs2 * dt + kSpeedSlackMs;
    if (jump || surge) {
      suspectMs_ = nowMs;
      LOGD("GPS", "suspect fix: moved %.0f m in %.2f s, speed %.1f -> %.1f m/s, sats %u", dist, dt,
           prevSpeed_, speed, (unsigned)gps_.satellites.value());
    }
  }
  havePrev_ = true;
  prevLat_ = lat;
  prevLon_ = lon;
  prevSpeed_ = speed;
  prevFixMs_ = nowMs;
}

bool Gps::steady(uint32_t nowMs) const {
  return havePrev_ && nowMs - prevFixMs_ <= kFixGapMs && nowMs - suspectMs_ >= kSteadyMs;
}

uint32_t Gps::utcEpoch() {
  if (!(gps_.date.isValid() && gps_.date.year() > 2020) || !(gps_.time.isValid() && gps_.time.age() < 3000)) return 0;
  // Days from civil date (Howard Hinnant's algorithm).
  int y = gps_.date.year(), m = gps_.date.month(), d = gps_.date.day();
  y -= m <= 2;
  int era = y / 400;
  int yoe = y - era * 400;
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  uint32_t days = era * 146097 + doe - 719468;
  return days * 86400 + gps_.time.hour() * 3600 + gps_.time.minute() * 60 + gps_.time.second();
}

void Gps::read(uint32_t nowMs, GpsData& out) {
  out.link = locked_;
  out.lat = gps_.location.lat();
  out.lon = gps_.location.lng();
  out.fix = gps_.location.isValid() && gps_.location.age() < 2500 && gps_.satellites.value() >= 4 &&
            gpsCoordsValid(out.lat, out.lon);
  out.steady = steady(nowMs);
  out.sats = gps_.satellites.value();
  out.hdop = gps_.hdop.isValid() ? gps_.hdop.hdop() : 99.0f;
  out.speedMs = gps_.speed.mps();
  out.courseDeg = gps_.course.deg();
  out.altValid = gps_.altitude.isValid() && gps_.altitude.age() < 3000;
  out.altM = gps_.altitude.meters();
  out.timeValid = gps_.time.isValid() && gps_.time.age() < 3000;
  out.dateValid = gps_.date.isValid() && gps_.date.year() > 2020;
  out.year = gps_.date.year();
  out.month = gps_.date.month();
  out.day = gps_.date.day();
  out.hour = gps_.time.hour();
  out.minute = gps_.time.minute();
  out.second = gps_.time.second();
  out.centisecond = gps_.time.centisecond();
  out.utcEpoch = utcEpoch();
}

