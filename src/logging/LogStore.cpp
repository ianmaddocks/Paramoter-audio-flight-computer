#include "LogStore.h"

#include <LittleFS.h>

#include <algorithm>

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
  if (ext.length() == 4 && (ext[0] == 's' || ext[0] == 't' || ext[0] == 'm' || ext[0] == 'u')) {
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
// other records (takeoff and landing marks) stay. With strip, each fix is cut to the 35 characters an IGC fix has (the RPM and the
// volumes after them go) and the slow records are dropped. The new stretch takes the letter `to`. Needs room for about a quarter of the old one.
bool thin(const Piece& p, char to, int step, bool strip) {
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
      if (keep && strip && n > 35) {  // B + time + position + flag + the two altitudes: 35 characters
        line[35] = '\r';
        line[36] = '\n';
        line[37] = 0;
        if (out.write((const uint8_t*)line, 37) != 37) {
          ok = false;
          break;
        }
        continue;
      }
    } else if (strncmp(line, "LXPVD,", 6) == 0) {
      keep = keepLast && !strip;
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

// A flight cut down to the minimum no longer has the extension fields its header declares: take the declaration (the I record) and
// the volume setup out of the header, so the file stays a valid IGC file.
void stripHeader(const String& id) {
  const String path = pathOf(id + ".hdr");
  File in = LittleFS.open(path, "r");
  if (!in) return;
  const String tmp = path + ".tmp";
  File out = LittleFS.open(tmp, "w");
  if (!out) {
    in.close();
    return;
  }
  char line[160];
  while (in.available()) {
    const size_t n = in.readBytesUntil('\n', line, sizeof(line) - 2);
    if (n == 0) continue;
    line[n] = '\n';
    line[n + 1] = 0;
    if (line[0] == 'I' || strncmp(line, "LXPVVOL,", 8) == 0) continue;
    out.write((const uint8_t*)line, n + 1);
  }
  in.close();
  out.close();
  LittleFS.remove(path);
  LittleFS.rename(tmp, path);
}

}  // namespace

size_t freeBytes() { return LittleFS.totalBytes() - LittleFS.usedBytes(); }

void list(std::vector<Flight>& out) {
  out.clear();
  File dir = LittleFS.open(kDir);
  if (!dir) return;
  struct Tagged {
    String id;
    Piece p;
  };
  std::vector<Tagged> all;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    Tagged t;
    if (!parseName(String(f.name()), t.id, t.p)) continue;
    t.p.size = f.size();
    all.push_back(t);
  }
  dir.close();
  // by flight, then header first and the stretches in order
  std::sort(all.begin(), all.end(), [](const Tagged& a, const Tagged& b) {
    if (a.id != b.id) return a.id < b.id;
    const int ra = a.p.kind == 'h' ? -1 : a.p.index, rb = b.p.kind == 'h' ? -1 : b.p.index;
    return ra < rb;
  });
  // oldest first: logs with no date (the "nodate" ids) before the dated ones
  std::vector<Flight> dated, nodate;
  for (const Tagged& t : all) {
    std::vector<Flight>& dst = t.id.startsWith("nodate") ? nodate : dated;
    if (dst.empty() || dst.back().id != t.id) {
      Flight f;
      f.id = t.id;
      dst.push_back(f);
    }
    dst.back().pieces.push_back(t.p);
    dst.back().size += t.p.size;
  }
  out = nodate;
  out.insert(out.end(), dated.begin(), dated.end());
}

bool find(const String& idIn, Flight& out) {
  const String id = idIn.endsWith(".igc") ? idIn.substring(0, idIn.length() - 4) : idIn;
  out = Flight();
  out.id = id;
  File dir = LittleFS.open(kDir);
  if (!dir) return false;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {  // only this flight's pieces are kept (no list of every flight)
    const String name = String(f.name());
    if (!name.startsWith(id) || name.length() <= id.length() || (name[id.length()] != '.')) continue;
    String pid;
    Piece p;
    if (!parseName(name, pid, p) || pid != id) continue;
    p.size = f.size();
    out.size += p.size;
    out.pieces.push_back(p);
  }
  dir.close();
  std::sort(out.pieces.begin(), out.pieces.end(), [](const Piece& a, const Piece& b) {
    const int ra = a.kind == 'h' ? -1 : a.index, rb = b.kind == 'h' ? -1 : b.index;
    return ra < rb;
  });
  return !out.pieces.empty();
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
  // 2. thin today's logs to one fix in 5, oldest first (the current flight's finished stretches too)
  for (const Flight& f : all) {
    if (olderDay(f.id, currentId) && f.id != currentId) continue;  // (gone already)
    for (const Piece& p : f.pieces) {
      if (p.kind != 's' || p.name == activePiece) continue;
      if (thin(p, 't', 5, false)) LOGI("IGC", "Thinned %s to one fix in 5 (%u kB free)", p.name.c_str(), (unsigned)(freeBytes() / 1024));
      if (freeBytes() >= minFree) return true;
    }
  }
  // 3. today's other logs cut down to the minimum an IGC file needs, still at one fix in 5, then at one in 20. A flight is converted
  // whole (its header loses the extension declaration), so it is never left half and half.
  for (int pass = 0; pass < 2; ++pass) {
    list(all);  // (the pieces have new names now: look again)
    for (const Flight& f : all) {
      if (f.id == currentId || olderDay(f.id, currentId)) continue;
      bool any = false;
      for (const Piece& p : f.pieces) {
        const bool todo = pass == 0 ? (p.kind == 's' || p.kind == 't') : p.kind == 'm';
        if (!todo || p.kind == 'u') continue;
        if (!any && pass == 0) stripHeader(f.id);
        any = true;
        thin(p, pass == 0 ? 'm' : 'u', pass == 0 ? 5 : 20, true);
      }
      if (any) {
        LOGI("IGC", "Cut %s down to the minimum at one fix in %d (%u kB free)", f.id.c_str(), pass == 0 ? 5 : 20, (unsigned)(freeBytes() / 1024));
        if (freeBytes() >= minFree) return true;
      }
    }
  }
  // 4. today's older logs
  list(all);
  for (const Flight& f : all) {
    if (f.id == currentId || olderDay(f.id, currentId)) continue;
    remove(f.id);
    LOGI("IGC", "Deleted log %s to make room (%u kB free)", f.id.c_str(), (unsigned)(freeBytes() / 1024));
    if (freeBytes() >= minFree) return true;
  }
  // 5. the start of the current flight
  list(all);
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
