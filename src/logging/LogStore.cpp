#include "LogStore.h"

#include <LittleFS.h>

#include <algorithm>
#include <map>

#include "Log.h"

namespace logstore {
namespace {

// Splits a file name of /logs into flight id and piece. False for a name that is none of ours.
bool parseName(const String& name, String& id, Piece& p) {
  p.name = name;
  p.index = 0;
  if (name.endsWith(".igc")) {
    id = name.substring(0, name.length() - 4);
    p.kind = 'g';
    return id.length() > 0;
  }
  const int dot = name.indexOf('.');
  if (dot <= 0) return false;
  id = name.substring(0, dot);
  const String ext = name.substring(dot + 1);
  if (ext == "hdr") {
    p.kind = 'h';
    return true;
  }
  if (ext.length() == 4 && (ext[0] == 's' || ext[0] == 't' || ext[0] == 'u')) {
    p.kind = ext[0];
    p.index = ext.substring(1).toInt();
    return true;
  }
  return false;
}

String pathOf(const String& name) { return String(kDir) + "/" + name; }

// Is this flight from an earlier day than the current one? (Ids start with the date, yymmdd.) A log with no date is old.
bool olderDay(const String& id, const String& currentId) {
  if (id.startsWith("nodate")) return true;
  if (currentId.length() < 6 || currentId.startsWith("nodate")) return false;
  return id.substring(0, 6) < currentId.substring(0, 6);
}

// Rewrites a stretch keeping one fix in `step` (by the second of the fix) and the slow record that follows a kept fix; the
// other records (takeoff and landing marks) stay. The new stretch takes the letter `to`. Needs room for about a quarter of the old one.
bool thin(const Piece& p, char to, int step) {
  File in = LittleFS.open(pathOf(p.name), "r");
  if (!in) return false;
  const String tmpPath = pathOf(p.name) + ".tmp";
  File out = LittleFS.open(tmpPath, "w");
  if (!out) {
    in.close();
    return false;
  }
  bool keepLast = true, wroteB = false, ok = true;
  char line[160];
  while (in.available()) {
    const size_t n = in.readBytesUntil('\n', line, sizeof(line) - 2);
    if (n == 0) continue;
    line[n] = '\n';
    line[n + 1] = 0;
    bool keep = true;
    if (line[0] == 'B' && n >= 7) {
      const int t = (line[1] - '0') * 36000 + (line[2] - '0') * 3600 + (line[3] - '0') * 600 + (line[4] - '0') * 60 + (line[5] - '0') * 10 +
                    (line[6] - '0');
      keep = !wroteB || (t % step) == 0;  // always the first fix of the stretch
      keepLast = keep;
      if (keep) wroteB = true;
    } else if (strncmp(line, "LXPVD,", 6) == 0) {
      keep = keepLast;
    }
    if (keep && out.write((const uint8_t*)line, n + 1) != n + 1) {
      ok = false;
      break;
    }
  }
  in.close();
  out.close();
  if (!ok) {
    LittleFS.remove(tmpPath);
    return false;
  }
  LittleFS.remove(pathOf(p.name));
  // <id>.s003 -> <id>.t003
  String base = p.name.substring(0, p.name.length() - 4);
  char idx[8];
  snprintf(idx, sizeof(idx), "%c%03d", to, p.index);
  return LittleFS.rename(tmpPath, pathOf(base + idx));
}

}  // namespace

size_t freeBytes() { return LittleFS.totalBytes() - LittleFS.usedBytes(); }

void list(std::vector<Flight>& out) {
  out.clear();
  File dir = LittleFS.open(kDir);
  if (!dir) return;
  std::map<String, Flight> byId;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String id;
    Piece p;
    if (!parseName(String(f.name()), id, p)) continue;
    p.size = f.size();
    Flight& fl = byId[id];
    fl.id = id;
    fl.size += p.size;
    fl.pieces.push_back(p);
  }
  dir.close();
  for (auto& kv : byId) {
    Flight& fl = kv.second;
    std::sort(fl.pieces.begin(), fl.pieces.end(), [](const Piece& a, const Piece& b) {
      const int ra = a.kind == 'h' ? -1 : a.index, rb = b.kind == 'h' ? -1 : b.index;
      return ra < rb;
    });
    out.push_back(fl);
  }
  // std::map orders by id: dates first, so oldest first ("nodate" ids sort after the digits: put them first)
  std::stable_sort(out.begin(), out.end(), [](const Flight& a, const Flight& b) {
    const bool na = a.id.startsWith("nodate"), nb = b.id.startsWith("nodate");
    if (na != nb) return na;
    return a.id < b.id;
  });
}

bool find(const String& idIn, Flight& out) {
  String id = idIn.endsWith(".igc") ? idIn.substring(0, idIn.length() - 4) : idIn;
  std::vector<Flight> all;
  list(all);
  for (const Flight& f : all) {
    if (f.id == id) {
      out = f;
      return true;
    }
  }
  return false;
}

bool remove(const String& id) {
  Flight f;
  if (!find(id, f)) return false;
  for (const Piece& p : f.pieces) LittleFS.remove(pathOf(p.name));
  return true;
}

bool makeRoom(size_t minFree, const String& currentId, const String& activePiece) {
  if (freeBytes() >= minFree) return true;
  std::vector<Flight> all;
  list(all);
  // 1. earlier days
  for (const Flight& f : all) {
    if (f.id == currentId || !olderDay(f.id, currentId)) continue;
    remove(f.id);
    LOGI("IGC", "Deleted log %s from an earlier day to make room (%u kB free)", f.id.c_str(), (unsigned)(freeBytes() / 1024));
    if (freeBytes() >= minFree) return true;
  }
  // 2. thin today's logs: 1 fix in 5, then 1 in 20; oldest first, the current flight's finished stretches too
  for (int pass = 0; pass < 2; ++pass) {
    const char from = pass == 0 ? 's' : 't', to = pass == 0 ? 't' : 'u';
    const int step = pass == 0 ? 5 : 20;
    for (const Flight& f : all) {
      if (olderDay(f.id, currentId) && f.id != currentId) continue;  // (gone already)
      for (const Piece& p : f.pieces) {
        if (p.kind != from || p.name == activePiece) continue;
        const uint32_t before = p.size;
        if (thin(p, to, step)) LOGI("IGC", "Thinned %s to one fix in %d (%u -> less, %u kB free)", p.name.c_str(), step, (unsigned)before, (unsigned)(freeBytes() / 1024));
        if (freeBytes() >= minFree) return true;
      }
    }
  }
  // 3. today's older logs
  for (const Flight& f : all) {
    if (f.id == currentId || olderDay(f.id, currentId)) continue;
    remove(f.id);
    LOGI("IGC", "Deleted log %s to make room (%u kB free)", f.id.c_str(), (unsigned)(freeBytes() / 1024));
    if (freeBytes() >= minFree) return true;
  }
  // 4. the start of the current flight
  for (const Flight& f : all) {
    if (f.id != currentId) continue;
    for (const Piece& p : f.pieces) {
      if (p.kind == 'h' || p.name == activePiece) continue;
      LittleFS.remove(pathOf(p.name));
      LOGW("IGC", "Trimmed the start of the flight %s (%s) to keep recording (%u kB free)", currentId.c_str(), p.name.c_str(),
           (unsigned)(freeBytes() / 1024));
      if (freeBytes() >= minFree) return true;
    }
  }
  return freeBytes() >= minFree;
}

bool Reader::open(const String& id) {
  close();
  if (!find(id, f_)) return false;
  total_ = f_.size;
  piece_ = 0;
  return !f_.pieces.empty();
}

int Reader::read(uint8_t* buf, size_t n) {
  while (piece_ < f_.pieces.size()) {
    if (!file_) {
      file_ = LittleFS.open(pathOf(f_.pieces[piece_].name), "r");
      if (!file_) {
        ++piece_;
        continue;
      }
    }
    const int got = file_.read(buf, n);
    if (got > 0) return got;
    file_.close();
    ++piece_;
  }
  return 0;
}

void Reader::close() {
  if (file_) file_.close();
  piece_ = 0;
  total_ = 0;
  f_ = Flight();
}

#if PV_TEST
bool makeFake(const String& id, int seconds, int startSecOfDay) {
  LittleFS.mkdir(kDir);
  File h = LittleFS.open(pathOf(id + ".hdr"), "w");
  if (!h) return false;
  h.print("AXPVParaVario\r\nI033639RPM4042VVL4345SVL\r\n");
  h.close();
  File s = LittleFS.open(pathOf(id + ".s000"), "w");
  if (!s) return false;
  for (int k = 0; k < seconds; ++k) {
    const int t = startSecOfDay + k;
    s.printf("B%02d%02d%02d5130000N00030000EA00100001000000020020\r\n", t / 3600, t / 60 % 60, t % 60);
    if (k % 5 == 0) s.print("LXPVD,25,250,40,390,70,150,10,12,40,20,20\r\n");
  }
  s.close();
  return true;
}
#endif

}  // namespace logstore
