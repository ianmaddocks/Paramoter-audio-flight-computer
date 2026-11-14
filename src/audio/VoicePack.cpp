#include "VoicePack.h"

#include <Arduino.h>
#include <LittleFS.h>

#include <new>
#include <vector>

#include "Log.h"
#include <esp_task_wdt.h>

namespace voicepack {
namespace {

constexpr char kPath[] = "/voice.pak";
constexpr int kNameLen = 20;
constexpr int kHeader = kNameLen + 4;
constexpr int kMaxClips = 160;

struct Entry {
  char name[kNameLen];
  uint32_t offset, size;
};
Entry* gIndex = nullptr;
int gCount = 0;
uint32_t gGoodEnd = 0;  // where the last complete record ends

}  // namespace

bool begin() {
  delete[] gIndex;
  gIndex = nullptr;
  gCount = 0;
  gGoodEnd = 0;
  File f = LittleFS.open(kPath, "r");
  if (!f) return false;
  char magic[4];
  if (f.read((uint8_t*)magic, 4) != 4 || memcmp(magic, "PVP1", 4) != 0) {
    f.close();
    return false;
  }
  gIndex = new (std::nothrow) Entry[kMaxClips];
  if (!gIndex) {
    f.close();
    return false;
  }
  const uint32_t total = f.size();
  uint32_t pos = 4;
  while (pos + kHeader <= total && gCount < kMaxClips) {
    uint8_t h[kHeader];
    if (!f.seek(pos) || f.read(h, kHeader) != kHeader) break;
    const uint32_t size = h[20] | (h[21] << 8) | (h[22] << 16) | ((uint32_t)h[23] << 24);
    if (pos + kHeader + size > total) break;  // a record cut short (a power cut while packing)
    memcpy(gIndex[gCount].name, h, kNameLen);
    gIndex[gCount].name[kNameLen - 1] = 0;
    gIndex[gCount].offset = pos + kHeader;
    gIndex[gCount].size = size;
    ++gCount;
    pos += kHeader + size;
  }
  gGoodEnd = pos;
  f.close();
  return gCount > 0;
}

static const Entry* find(const char* name) {
  for (int i = 0; i < gCount; ++i)
    if (strcmp(gIndex[i].name, name) == 0) return &gIndex[i];
  return nullptr;
}

bool open(const char* name, File& f) {
  const Entry* e = gIndex ? find(name) : nullptr;
  if (!e) return false;
  f = LittleFS.open(kPath, "r");
  if (!f) return false;
  if (!f.seek(e->offset)) {
    f.close();
    return false;
  }
  return true;
}

void migrate() {
  File dir = LittleFS.open("/v");
  if (!dir) return;
  std::vector<String> names;
  for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    String n = e.name();
    if (n.endsWith(".adp")) names.push_back(n);
  }
  dir.close();
  if (names.empty()) {
    LittleFS.rmdir("/v");
    return;
  }
  // A pack left half written (or not a pack): start it again. The clips already moved out of /v by an interrupted
  // migration are lost with it: a filesystem upload brings them back.
  begin();
  {
    File f = LittleFS.open(kPath, "r");
    const bool exists = (bool)f;
    const uint32_t size = exists ? f.size() : 0;
    if (f) f.close();
    if (exists && (gGoodEnd < 4 || size != gGoodEnd)) {
      LOGW("AUD", "Voice pack was incomplete: started again");
      LittleFS.remove(kPath);
      delete[] gIndex;
      gIndex = nullptr;
      gCount = 0;
    }
  }
  LOGI("AUD", "Packing %d voice clips into %s (once, a few seconds)", (int)names.size(), kPath);
  File pak = LittleFS.open(kPath, "a");
  if (!pak) {
    LOGE("AUD", "Cannot open %s", kPath);
    return;
  }
  if (pak.size() == 0) pak.write((const uint8_t*)"PVP1", 4);
  int moved = 0;
  for (const String& fn : names) {
    String base = fn.substring(0, fn.length() - 4);
    String path = String("/v/") + fn;
    if (gIndex && find(base.c_str())) {  // already in the pack: just the old file to delete
      LittleFS.remove(path);
      continue;
    }
    File src = LittleFS.open(path, "r");
    if (!src || base.length() >= (unsigned)kNameLen) continue;
    const uint32_t size = src.size();
    if (LittleFS.totalBytes() - LittleFS.usedBytes() < size + 8192) {
      LOGW("AUD", "No room to pack %s: stopped (the old clips still play)", fn.c_str());
      src.close();
      break;
    }
    const uint32_t before = pak.size();
    uint8_t h[kHeader] = {0};
    memcpy(h, base.c_str(), base.length());
    h[20] = size & 0xFF;
    h[21] = (size >> 8) & 0xFF;
    h[22] = (size >> 16) & 0xFF;
    h[23] = (size >> 24) & 0xFF;
    pak.write(h, kHeader);
    uint8_t buf[512];
    int n;
    while ((n = src.read(buf, sizeof(buf))) > 0) pak.write(buf, n);
    src.close();
    pak.flush();
    if (pak.size() != before + kHeader + size) {
      LOGE("AUD", "Writing %s failed: stopped", fn.c_str());
      break;
    }
    LittleFS.remove(path);  // only after the whole record is stored
    ++moved;
    esp_task_wdt_reset();
  }
  pak.close();
  LittleFS.rmdir("/v");  // succeeds only when it is empty
  begin();
  LOGI("AUD", "Voice pack: %d clips packed now, %d in the pack, %u kB free", moved, gCount,
       (unsigned)((LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024));
}

}  // namespace voicepack
