#include "Speech.h"

#include <math.h>

#include "Settings.h"
#include "nav/Geo.h"

namespace speech {
namespace {

const char* const kOnes[20] = {"n0",  "n1",  "n2",  "n3",  "n4",  "n5",  "n6",
                               "n7",  "n8",  "n9",  "n10", "n11", "n12", "n13",
                               "n14", "n15", "n16", "n17", "n18", "n19"};
const char* const kTens[10] = {nullptr, nullptr, "n20", "n30", "n40",
                               "n50",   "n60",   "n70", "n80", "n90"};

constexpr float kFtPerM = 3.28084f;

void addBelow1000(audio::Utterance& u, int n) {
  if (n >= 100) {
    u.add(kOnes[n / 100]).add("hundred");
    n %= 100;
    if (n == 0) return;
  }
  if (n < 20) {
    u.add(kOnes[n]);
  } else {
    u.add(kTens[n / 10]);
    if (n % 10) u.add(kOnes[n % 10]);
  }
}

int roundTo(float v, int step) { return (int)lroundf(v / step) * step; }

}  // namespace

void addNumber(audio::Utterance& u, int n) {
  if (n < 0) {
    u.add("minus");
    n = -n;
  }
  if (n == 0) {
    u.add(kOnes[0]);
    return;
  }
  n = n > 999999 ? 999999 : n;
  if (n >= 1000) {
    addBelow1000(u, n / 1000);
    u.add("thousand");
    n %= 1000;
    if (n == 0) return;
  }
  addBelow1000(u, n);
}

void addDecimal(audio::Utterance& u, float v) {
  int tenths = (int)lroundf(fabsf(v) * 10.0f);
  if (v < 0) u.add("minus");
  addNumber(u, tenths / 10);
  if (tenths % 10) u.add("point").add(kOnes[tenths % 10]);
}

void addHeight(audio::Utterance& u, float metres) {
  if (g_cfg.heightUnit == HU_FEET) {
    float ft = metres * kFtPerM;
    addNumber(u, roundTo(ft, fabsf(ft) < 1000 ? 10 : 50));
    u.add("feet");
  } else {
    addNumber(u, roundTo(metres, fabsf(metres) < 300 ? 5 : 10));
    u.add("metres");
  }
}

void addDistance(audio::Utterance& u, float metres) {
  metres = fabsf(metres);
  if (g_cfg.distUnit == DU_FEET) {
    float ft = metres * kFtPerM;
    if (ft < 2640) {
      addNumber(u, roundTo(ft, 50));
      u.add("feet");
    } else {
      addDecimal(u, ft / 5280.0f);
      u.add("miles");
    }
  } else if (metres < 1000) {
    addNumber(u, roundTo(metres, metres < 100 ? 5 : 10));
    u.add("metres");
  } else {
    addDecimal(u, metres / 1000.0f);
    u.add("kilometres");
  }
}

void addClock(audio::Utterance& u, float relDeg) {
  addNumber(u, geo::clockFace(relDeg));
  u.add("oclock");
}

void addDuration(audio::Utterance& u, uint32_t seconds) {
  uint32_t h = seconds / 3600;
  uint32_t m = (seconds % 3600) / 60;
  if (h) {
    addNumber(u, h);
    u.add(h == 1 ? "hour" : "hours");
  }
  if (m || !h) {
    addNumber(u, m);
    u.add(m == 1 ? "minute" : "minutes");
  }
}

void addSpeed(audio::Utterance& u, float ms) {
  if (g_cfg.speedUnit == SU_MPH) {
    addNumber(u, (int)lroundf(ms * 2.23694f));
    u.add("mph");
  } else if (g_cfg.speedUnit == SU_KNOTS) {
    addNumber(u, (int)lroundf(ms * 1.94384f));
    u.add("knots");
  } else {
    addNumber(u, (int)lroundf(ms * 3.6f));
    u.add("kmh");
  }
}

void addTurn(audio::Utterance& u, bool right) { u.add(right ? "turn_right_now" : "turn_left_now"); }

void say(audio::Priority p, const char* clip) {
  audio::Utterance u(p);
  u.add(clip);
  audio::say(u);
}

}  // namespace speech
