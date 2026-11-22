#pragma once
#include <Arduino.h>

#include "nav/Nav.h"

// Flying areas: up to kMaxAreas polygons (up to kMaxPoints corners each, flat, at every height) drawn on the web page and kept in
// /areas.json. Each is a safe area (a place to stay inside) or a restricted one (a place to keep out of), with a warning distance:
//   Safe       0 = "You have left the safe area" only; more = also "Safe area edge, N metres" when that close to an edge (no directions).
//   Restricted 0 = only when inside; more = "Restricted area, N metres, turn right to 2 o'clock" when the track would enter it within
//              that distance, and "You are in a restricted area, turn ..." inside it.
// See docs/Feature_Reference.md.
class SafeArea {
 public:
  static constexpr int kMaxAreas = 3;
  static constexpr int kMaxPoints = 24;
  static constexpr int kNameLen = 24;
  static constexpr int kMaxWarnM = 5000;
  enum class State : uint8_t { None, Inside, Edge, Outside };         // the safe areas
  enum class RState : uint8_t { Clear, Near, Inside };               // the restricted areas
  enum Kind : uint8_t { Safe = 0, Restricted = 1 };

  void begin();  // loads the stored areas
  // Replaces all the areas from the page's JSON {"a":[{"n":name,"on":true,"k":"safe","w":0,"p":[[lat,lon],...]}]} and stores them.
  // false and a reason in *err when the data is not usable (nothing is changed then).
  bool setFromJson(const char* json, const char** err);
  void toJson(String& out) const;

  // Called every flight tick with the navigation state; guidanceActive = a landing approach or emergency glide is running (its
  // calls come first, so this stays quiet). (It also keeps the state shown on the page up to date on the ground.)
  void update(const NavState& nav, bool guidanceActive);
  void reset();  // forget the state (not the areas)

  int safeCount() const { return count(Safe); }
  int restrictedCount() const { return count(Restricted); }
  State state() const { return state_; }
  RState restrictedState() const { return rstate_; }
  float edgeM() const { return edgeM_; }       // distance to the nearest edge of a safe area, in or out; -1 when unknown
  float restrictedM() const { return rDistM_; }  // distance ahead to (or to the edge of) a restricted area; -1 when clear
  const char* stateName() const;
  const char* restrictedStateName() const;

 private:
  struct Area {
    char name[kNameLen] = "";
    bool on = false;
    uint8_t kind = Safe;
    uint8_t n = 0;
    uint16_t warnM = 0;
    int32_t lat[kMaxPoints] = {};  // micro-degrees
    int32_t lon[kMaxPoints] = {};
  };
  Area areas_[kMaxAreas];
  int count(Kind k) const;
  bool save() const;
  static bool parse(const char* json, Area* out, const char** err);

  State state_ = State::None;
  RState rstate_ = RState::Clear;
  float edgeM_ = -1, rDistM_ = -1;
  uint32_t lastEvalMs_ = 0;
  uint32_t lastSpokeMs_ = 0, lastRSpokeMs_ = 0;
  uint8_t outsideCount_ = 0, insideCount_ = 0, rInsideCount_ = 0;
};
