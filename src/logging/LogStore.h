#pragma once
#include <Arduino.h>
#include <FS.h>

#include <vector>

// Where the flight logs live and how space is made for new ones.
//
// A flight is stored in pieces under /logs, all named after the flight (its GPS date and time, "yymmdd-hhmmss"):
//   <id>.hdr    the file header (the A, H, I and L records), written once when the flight starts
//   <id>.s000   the first stretch of fixes (about 32 kB, 10 minutes), then .s001, .s002 ... one after the other
//   .t000       a stretch that was thinned to one fix in 5 (to make room),  .u000 one fix in 20
//   <id>.igc    a log from older firmware: one whole file
// Downloading a flight sends the header and then the stretches in order, so it is the same IGC file as ever. Pieces are what
// make it cheap to make room in the middle of a long flight: the oldest stretch is simply deleted (the start is trimmed), or
// rewritten with fewer fixes, and nothing has to be copied.
namespace logstore {

constexpr const char* kDir = "/logs";

struct Piece {
  String name;      // the file name in /logs
  char kind;        // 'h' header, 's' / 't' / 'u' a stretch (all fixes / 1 in 5 / 1 in 20), 'g' a whole log from older firmware
  int index;        // the stretch number
  uint32_t size;
};

struct Flight {
  String id;
  std::vector<Piece> pieces;  // header first, then the stretches in order
  uint32_t size = 0;
};

// Every flight, oldest first.
void list(std::vector<Flight>& out);

// The flight called id (with or without ".igc"); false when there is none.
bool find(const String& id, Flight& out);

// Deletes every piece of a flight. False when there was nothing to delete.
bool remove(const String& id);

size_t freeBytes();

// Makes at least minFree bytes free, doing as little harm as it can, in this order:
//   1. delete logs from earlier days (the owner downloads each day's logs at the end of the day),
//   2. thin today's logs to one fix in 5, oldest first, then to one in 20 (the current flight's finished stretches too),
//   3. delete today's older logs,
//   4. trim the start of the current flight (its oldest finished stretches).
// currentId is the flight being recorded ("" when none) and activePiece the stretch being written (never touched).
// Returns true when minFree is free.
bool makeRoom(size_t minFree, const String& currentId, const String& activePiece);

// Reading a flight as one stream: header, then the stretches in order.
class Reader {
 public:
  bool open(const String& id);
  uint32_t total() const { return total_; }
  int read(uint8_t* buf, size_t n);  // 0 at the end
  void close();

 private:
  Flight f_;
  size_t piece_ = 0;
  File file_;
  uint32_t total_ = 0;
};

#if PV_TEST
// Test build: a made-up flight of `seconds` fixes starting at hh:mm:ss of the day `id` (for the tests of making room).
bool makeFake(const String& id, int seconds, int startSecOfDay);
#endif

}  // namespace logstore
